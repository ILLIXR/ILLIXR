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

./main.opt.exe -y ../illixr.yaml