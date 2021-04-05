#!/bin/bash

#--- Script for installing DBoW2 ---#


### Setup ###

## Source the default values for imported variables
. scripts/default_values.sh

## Source the global helper functions
. scripts/system_utils.sh

## Source the configurations for our dependencies
. deps.sh


### Package metadata setup ###

if [ -z "${dep_name}" ]; then
    dep_name="${dep_name_dbow2}"
fi

if [ -z "${src_dir}" ]; then
    src_dir="${parent_dir_dbow2}/${dep_name_dbow2}"
fi

if [ -z "${dep_ver}" ]; then
    dep_ver="${dep_ver_dbow2}"
fi

repo_url="https://github.com/dorian3d/DBoW2.git"
build_dir="${src_dir}/build"


### Fetch, build and install ###

## Fetch
git clone --depth 1 --branch "${dep_ver}" "${repo_url}" "${src_dir}"

## Build
cmake \
	-S "${src_dir}" \
	-B "${build_dir}" \
    -D CMAKE_BUILD_TYPE="${build_type}" \
    -D CMAKE_INSTALL_PREFIX="${prefix_dir}"
make -C "${build_dir}" -j "${illixr_nproc}"

## Install
sudo make -C "${build_dir}" -j "${illixr_nproc}" install

## Log
log_dependency "${dep_name}" "${deps_log_dir}" "${src_dir}" "${dep_ver}"
