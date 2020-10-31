# Getting Started

## ILLIXR Runtime

These instructions have been tested with Ubuntu 18.04 and 20.04.

1. Clone the repository:

        git clone --recursive --branch v2-latest https://github.com/ILLIXR/ILLIXR


2. Update the submodules. Submodules are git repositories inside a git repository that need to be
   pulled down separately:

        git submodule update --init --recursive

3. Install dependencies. This script installs some Ubuntu/Debian packages and builds several dependencies
   from source:

        ./install_deps.sh

4. Inspect `configs/native.yaml`. The schema definition is in `runner/config_schema.yaml`. For more
   details on the runner and the config files, see [Building ILLIXR][6].

5. Build and run ILLIXR standalone:

        ./runner.sh configs/native.yaml

6. If so desired, you can run ILLIXR headlessly using [xvfb][5]:

        ./runner.sh configs/headless.yaml

## ILLIXR Runtime with Monado (supports OpenXR)

ILLIXR leverages [Monado][3], an open-source implementation of [OpenXR][4], to support a wide range
of applications. Because of a low-level driver issue, Monado only supports Ubuntu 18.04+.

6. Compile and run:

        ./runner.sh configs/monado.yaml

## ILLIXR Standalone

ILLIXR can also benchmark each component in isolation.

1. Clone the repository.

        git clone --recursive --branch v1-latest https://github.com/ILLIXR/ILLIXR

2. Update the submodules. Submodules are git repositories inside a git repository that need to be
   pulled down separately.

        git submodule update --init --recursive

3. Each component is a directory in `benchmark`. See those components for their documentation.

## Virtual Machine

ILLIXR can be run inside a Qemu-KVM image. Check out the instructions [here][7].

## Next steps

 The source code is divided into the following directories:
- `runtime`: create a runnable binary that loads every plugin.
    * This contains Spindle, which is responsible for loading plugins.

- `common`: resources one might use in each plugin. Most plugins symlink this directory into theirs.
    * Contains the interface for Switchboard, which maintains event-streams (implementation is in `runtime`).
    * Contains the interface for Phonebook, which is a service-directory (implementation is in `runtime`).

- a directory for each plugin. Almost all of the XR functionality is implemented in plugins. See
  [Default Components][1] for more details.

Try browsing the source of plugins. If you edit any of the source files, the runner will
detect and rebuild the respective binary. If you want to add your own, see [Writing Your Plugin][2].

[1]: default_plugins.md
[2]: writing_your_plugin.md
[3]: https://monado.dev/
[4]: https://www.khronos.org/openxr/
[5]: http://manpages.ubuntu.com/manpages/bionic/man1/Xvfb.1.html
[6]: building_illixr.md
[7]: https://github.com/ILLIXR/ILLIXR/blob/master/qemu/INSTRUCTIONS.md
