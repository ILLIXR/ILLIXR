# Getting Started

## ILLIXR Runtime

These instructions have been tested with Ubuntu 18.10.

1. Clone the repository:

        git clone --recursive --branch v2-latest https://github.com/ILLIXR/ILLIXR


2. Update the submodules. Submodules are git repositories inside a git repository that need to be
   pulled down separately:

        git submodule update --init --recursive

3. Install dependencies. This script installs some Ubuntu/Debian packages and builds a specific
   version of OpenCV from source:

        ./install_deps.sh

4. Inspect `config.yaml`. The schema definition (with documentation inline) is in `runner/config_schema.yaml`.

5. Build and run ILLIXR standalone:

        ./runner.sh config.yaml

## ILLIXR Runtime with Monado (supports OpenXR)

ILLIXR leverages [Monado][3], an open-source implementation of [OpenXR][4], to support a wide range
of applications.  Monado only supports Ubuntu 18.10, because of a low-level driver issue.

1. Clone Monado:

        git clone https://github.com/ILLIXR/monado_integration.git

2. Clone our application example:

        git clone https://gitlab.freedesktop.org/monado/demos/openxr-simple-example

3. Clone ILLIXR:

        git clone --recursive --branch v2-latest https://github.com/ILLIXR/ILLIXR
        git submodule update --init --recursive

4. Install dependencies:

        cd ILLIXR
        ./install_deps.sh

5. Edit the `conig.yaml`, using this for the loader:

```yaml
loader:
  name: monado
  monado:
    path: ../monado_integration
  openxr_app:
    path: ../Monado_OpenXR_Simple_Example
```

5. Compile and run:

        ./runner.sh config.yaml

## ILLIXR Standalone

ILLIXR can also benchmark each component in isolation.

1. Clone the repository.

        git clone --recursive --branch v1-latest https://github.com/ILLIXR/ILLIXR


2. Update the submodules. Submodules are git repositories inside a git repository that need to be
   pulled down separately.

        git submodule update --init --recursive

3. Each component is a directory in `benchmark`. See those components for their documentation.

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
[3]: https://monado.dev/
[4]: https://www.khronos.org/openxr/
