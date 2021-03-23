#!/bin/bash

#--- Script for installing opengv ---#


### Setup ###

## Source the default values for imported variables
. scripts/default_values.sh

## Source the global helper functions
. scripts/system_utils.sh


### Package metadata setup ###

repo_url="https://github.com/laurentkneip/opengv.git"
opengv_dir="${opt_dir}/opengv"
build_dir="${opengv_dir}/build"
eigen_include_dir="${prefix_dir}/include/gtsam/3rdparty/Eigen"


### Fetch, build and install ###

## Fetch
git clone --depth 1 "${repo_url}" "${opengv_dir}"

## Build
cmake \
    -S "${opengv_dir}" \
    -B "${build_dir}" \
    -D CMAKE_BUILD_TYPE="${build_type}" \
    -D CMAKE_INSTALL_PREFIX="${prefix_dir}"
make -C "${build_dir}" -j "${illixr_nproc}"

## Install
sudo make -C "${build_dir}" -j "${illixr_nproc}" install
