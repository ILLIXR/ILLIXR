#!/bin/bash

git clone https://github.com/laurentkneip/opengv.git "${temp_dir}/opengv"

cmake \
	-S "${temp_dir}/opengv" \
	-B "${temp_dir}/opengv/build" \
    -D EIGEN_INCLUDE_DIR="${temp_dir}/gtsam/gtsam/3rdparty/Eigen" \
    -D EIGEN_INCLUDE_DIRS="${temp_dir}/gtsam/gtsam/3rdparty/Eigen"

sudo make -C "${temp_dir}/opengv/build" "-j$(nproc)" install
