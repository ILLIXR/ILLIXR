#!/bin/bash

git clone https://github.com/laurentkneip/opengv.git "${opt_dir}/opengv"

cmake \
	-S "${opt_dir}/opengv" \
	-B "${opt_dir}/opengv/build" \
    -D EIGEN_INCLUDE_DIR="${opt_dir}/gtsam/gtsam/3rdparty/Eigen" \
    -D EIGEN_INCLUDE_DIRS="${opt_dir}/gtsam/gtsam/3rdparty/Eigen"

sudo make -C "${opt_dir}/opengv/build" "-j${illixr_nproc}" install
