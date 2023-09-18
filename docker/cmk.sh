#!/bin/bash
# this script builds a full version of illixr

cd ILLIXR
rm -rf build
mkdir build
cd build
cmake .. -DBUILD_GROUP=ALL -DCMAKE_INSTALL_PREFIX=/home/illixr
make -j3
make install
