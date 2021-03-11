#!/bin/bash

#--- Script for installing opengv ---#


### Default imported variables setup ###

if [ -z "${opt_dir}" ]; then
    opt_dir="/opt/ILLIXR"
fi

if [ -z "${prefix_dir}" ]; then
    prefix_dir="/usr/local"
fi

if [ -z "${illixr_nproc}" ]; then
    illixr_nproc="1"
fi

if [ -z "${build_type}" ]; then
    build_type="Release"
fi


### Helper functions ###

## Source the global helper functions
. bash_utils.sh


### Package metadata setup ###

repo_url="https://github.com/laurentkneip/opengv.git"
opengv_dir="${opt_dir}/opengv"
eigen_include_dir="${prefix_dir}/include/gtsam/3rdparty/Eigen"


### Fetch, build and install ###

## Fetch
git clone "${repo_url}" "${opengv_dir}"

## Build
cmake \
	-S "${opengv_dir}" \
	-B "${opengv_dir}/build" \
	-D CMAKE_BUILD_TYPE="${build_type}" \
	-D CMAKE_INSTALL_PREFIX="${prefix_dir}" \
    -D EIGEN_INCLUDE_DIR="${eigen_include_dir}" \
    -D EIGEN_INCLUDE_DIRS="${eigen_include_dir}"
make -C "${opengv_dir}/build" "-j${illixr_nproc}"

## Install
sudo make -C "${opengv_dir}/build" "-j${illixr_nproc}" install
