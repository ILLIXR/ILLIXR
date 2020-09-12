#!/bin/bash

git clone https://github.com/google/googletest --branch release-1.10.0 "${opt_dir}/googletest"
cmake -S "${opt_dir}/googletest" -B "${opt_dir}/googletest/build"
make -C "${opt_dir}/googletest/build" "-j$(nproc)"
