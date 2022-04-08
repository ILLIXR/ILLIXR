# Getting Started

These instructions have been tested with Ubuntu 18.04 and 20.04.


## ILLIXR Runtime without Monado

1.  **Clone the repository**:

    <!--- language: lang-none -->

        git clone https://github.com/ILLIXR/ILLIXR

    ***Note for ILLIXR versions older than `v2.2.0`***:

    Update the submodules.
    Submodules are git repositories inside a git repository that need to be pulled down separately:

    <!--- language: lang-none -->

        git submodule update --init --recursive

1.  **Install dependencies**:

    <!--- language: lang-shell -->

        ./install_deps.sh [--jobs <integer>]

    This script installs some Ubuntu/Debian packages and builds several dependencies from source.
    Without any arguments, the script will print the help message, and proceed using default values.
    To change the number of threads/tasks to use for building, specify using the `--jobs` argument.
    Other available options can be inspected using the `--help` flag.

1.  **Inspect `configs/native.yaml`**.

    The schema definition is in `runner/config_schema.yaml`.
    For more details on the runner and the config files, see [Building ILLIXR][12].

1.  **Build and run ILLIXR without Monado**:

    <!--- language: lang-shell -->

        ./runner.sh configs/native.yaml

    If you are running ILLIXR without a graphical environment,
        try ILLIXR headlessly using [Xvfb][17]:

    <!--- language: lang-shell -->

        ./runner.sh configs/headless.yaml

1.  **To clean up after building, run**:

    <!--- language: lang-shell -->

        ./runner.sh configs/clean.yaml

    Or simply:

    <!--- language: lang-shell -->

        ./clean.sh


## ILLIXR Runtime with Monado

ILLIXR leverages [Monado][18], an open-source implementation of [OpenXR][19],
    to support a wide range of OpenXR client applications.
Because of a low-level driver issue, Monado only supports Ubuntu 18.04+.

1.  Compile and run:

    <!--- language: lang-shell -->

        ./runner.sh configs/monado.yaml


## ILLIXR under Virtualization

ILLIXR can be run inside a [_QEMU-KVM_][20] image.
Check out the instructions [here][16].


## Next Steps

Try browsing the source for the runtime and provided plugins.
The source code is divided into components in the following directories:

-   `ILLIXR/runtime/`:
    A directory holding the implementation for loading and interfacing plugins.
    This directory contains [_Spindle_][13].

-   `ILLIXR/common/`:
    A directory holding resources and utilities available globally to all plugins.
    Most plugins symlink this directory into theirs.
    This directory contains the interfaces for [_Switchboard_][14] and [_Phonebook_][15].

-   `ILLIXR/<plugin_dir>/`:
    A unique directory for each plugin.
    Most of the core XR functionality is implemented via plugins.
    See [Default Components][10] for more details.

If you edit any of the source files, the runner will detect and rebuild the respective binary
    the next time it runs.
If you want to add your own plugin, see [Writing Your Plugin][11].

Otherwise, proceed to the next section, [Building ILLIXR][12].


[//]: # (- Internal -)

[10]:   illixr_plugins.md
[11]:   writing_your_plugin.md
[12]:   building_illixr.md
[13]:   glossary.md#spindle
[14]:   glossary.md#switchboard
[15]:   glossary.md#phonebook
[16]:   virtualization.md
[17]:   glossary.md#xvfb
[18]:   glossary.md#monado
[19]:   glossary.md#openxr
[20]:   glossary.md#qemu-kvm
