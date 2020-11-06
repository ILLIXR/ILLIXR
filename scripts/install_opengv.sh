#!/bin/bash

git clone https://github.com/laurentkneip/opengv.git  "${temp_dir}/opengv"

# Replace path to your GTSAM's Eigen
cmake
    -DEIGEN_INCLUDE_DIR=/home/tonirv/Code/gtsam/gtsam/3rdparty/Eigen
    -DEIGEN_INCLUDE_DIRS=/home/tonirv/Code/gtsam/gtsam/3rdparty/Eigen

sudo make -j "${temp_dir}/opengv/build" $(nproc) install
