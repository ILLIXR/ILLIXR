#!/bin/bash

eval $( fixuid )

cd /opt/ILLIXR
mkdir build
cd build
cmake .. -DYAML_FILE=profiles/native_vk.yaml -DCMAKE_INSTALL_PREFIX=/opt/ILLIXR/build/install

# get nproc
NPROC=$(nproc)
cmake --build . -j$NPROC
cmake --install .

# run tests
export LD_LIBRARY_PATH=/opt/ILLIXR/build/install/lib:$LD_LIBRARY_PATH
export PATH=/opt/ILLIXR/build/install/bin:$PATH

# Set up XDG runtime directory
export XDG_RUNTIME_DIR=/tmp/runtime-docker
mkdir -p ${XDG_RUNTIME_DIR}
chmod 700 ${XDG_RUNTIME_DIR}

# Set display variable if not already set
export DISPLAY=${DISPLAY:-:1}

./main.opt.exe -y ../illixr.yaml