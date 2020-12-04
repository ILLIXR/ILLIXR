#!/bin/bash

git clone --branch kimera-gtsam https://github.com/ILLIXR/gtsam.git "${opt_dir}/gtsam"

cmake \
	-S "${opt_dir}/gtsam" \
	-B "${opt_dir}/gtsam/build" \
	-D CMAKE_INSTALL_PREFIX=/usr/local \
	-D GTSAM_WITH_TBB=OFF \
	-D GTSAM_USE_SYSTEM_EIGEN=OFF \
	-D GTSAM_POSE3_EXPMAP=ON \
	-D GTSAM_ROT3_EXPMAP=ON
sudo make -C "${opt_dir}/gtsam/build" "-j${illixr_nproc}" install
