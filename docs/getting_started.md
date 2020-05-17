# Getting started

1. Clone the repository.

        git clone https://github.com/charmoniumQ/illixr-prototype

    TODO: update this link when we move repositories

2. Update the submodules. Submodules are git repositories inside a git repository that need to be
   pulled down separately.

        git submodule update --init

3. Install dependencies. This script installs some Ubuntu/Debian packages and builds a specific
   version of OpenCV from source.

        ./install_deps.sh

4. Build and run ILLIXR standalone.

        make run.dbg

That's it! For more information on developing your own component see `components.md`.
