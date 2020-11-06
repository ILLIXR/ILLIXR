#!/bin/bash

git clone https://github.com/laurentkneip/opengv.git  "${temp_dir}/opengv"

cmake \
	-S "${temp_dir}/opengv" \
	-B "${temp_dir}/opengv/build" \
    -DEIGEN_INCLUDE_DIR="${temp_dir}/gtsam/gtsam/3rdparty/Eigen" \
    -DEIGEN_INCLUDE_DIRS="${temp_dir}/gtsam/gtsam/3rdparty/Eigen"
sudo make -j "${temp_dir}/opengv/build" $(nproc) install
