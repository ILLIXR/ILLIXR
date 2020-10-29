#!/bin/bash

git clone --branch master https://github.com/borglab/gtsam.git "${temp_dir}/gtsam"
cmake \
	-S "${temp_dir}/gtsam" \
	-B "${temp_dir}/gtsam/build" \
	-D CMAKE_INSTALL_PREFIX=/usr/local \
	-D CMAKE_BUILD_TYPE=RelWithDebInfo \
	-D GTSAM_USE_SYSTEM_EIGEN=OFF \
	-D GTSAM_POSE3_EXPMAP=ON \
	-D GTSAM_ROT3_EXPMAP=ON
sudo make -C "${temp_dir}/gtsam/build" "-j$(nproc)" install
