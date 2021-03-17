#!/bin/bash

#--- Script for installing eigen ---#


### Setup ###

## Source the default values for imported variables
. scripts/default_values.sh

## Source the global helper functions
. scripts/bash_utils.sh


### Package metadata setup ###

branch_tag_name="3.3.9"
repo_url="https://gitlab.com/libeigen/eigen.git"
eigen_dir="${opt_dir}/eigen"
build_dir="${eigen_dir}/build"


### Fetch, build and install ###

## Fetch
git clone --depth 1 --branch "${branch_tag_name}" "${repo_url}" "${eigen_dir}"

## Build
cmake \
    -S "${eigen_dir}" \
    -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE="${build_type}" \
    -DCMAKE_INSTALL_PREFIX="${prefix_dir}" \
    ..
# Make currently does nothing for eigen (only headers are in the repository)
make -C "${build_dir}" -j "${illixr_nproc}"

## Install
sudo make -C "${build_dir}" -j "${illixr_nproc}" install
