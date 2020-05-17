# Getting Started

1. Clone the repository.

        git clone --recursive https://github.com/charmoniumQ/illixr-prototype

    TODO: update this link when we move repositories

2. Update the submodules. Submodules are git repositories inside a git repository that need to be
   pulled down separately.

        git submodule update --init

3. Install dependencies. This script installs some Ubuntu/Debian packages and builds a specific
   version of OpenCV from source.

        ./install_deps.sh

4. Build and run ILLIXR standalone.

        make run.dbg

This runs ILLIXR with the default set of included plugins. The source code is divided into the following directories:
- `runtime`: create a runnable binary that loads every plugin.
  - This contains Spindle, which is responsible for loading plugins.

- `common`: resources one might use in each plugin. Most plugins symlink this directory into theirs.
  - Contains the interface for Switchboard, which maintains event-streams (implementation is in `runtime`).
  - Contains the interface for Phonebook, which is a service-directory (implementation is in `runtime`).

- a directory for each plugin. Almost all of the XR functionality is implemented in plugins. See
  `default_components.md` for more details.

Try browsing the source of plugins.  If you edit any of the source files, this make commend will
detect and rebuild the respective binary. If you want to add your own, see `writing_your_plugin.md`
