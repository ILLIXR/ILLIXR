#!/bin/bash

#--- Script for installing eigen ---#


### Setup ###

## Source the default values for imported variables
. scripts/default_values.sh

## Source the global helper functions
. scripts/system_utils.sh

## Source the configurations for our dependencies
. deps.sh


### Package metadata setup ###

if [ -z "${dep_name}" ]; then
    dep_name="${dep_name_eigen}"
fi

if [ -z "${src_dir}" ]; then
    src_dir="${parent_dir_eigen}/${dep_name_eigen}"
fi

if [ -z "${dep_ver}" ]; then
    dep_ver="${dep_ver_eigen}"
fi

repo_url="https://gitlab.com/libeigen/eigen.git"
build_dir="${src_dir}/build"


### Checks ###

## Assert no system packages will be overwritten by this install
## If present, remove the conflicting packages before proceeding
pkg_list_eigen="libeigen3-dev"
detect_packages "${pkg_list_eigen}" "${PKG_MODE_FOUND_NONFATAL}"


### Fetch, build and install ###

## Fetch
git clone --depth 1 --branch "${dep_ver}" "${repo_url}" "${src_dir}"

## Build
cmake \
    -S "${src_dir}" \
    -B "${build_dir}" \
    -D CMAKE_C_COMPILER="${CC}" \
    -D CMAKE_CXX_COMPILER="${CXX}" \
    -D CMAKE_BUILD_TYPE="${build_type}" \
    -D CMAKE_INSTALL_PREFIX="${prefix_dir}"
# Make currently does nothing for eigen (only headers are in the repository)
make -C "${build_dir}" -j "${illixr_nproc}"

## Install
sudo make -C "${build_dir}" -j "${illixr_nproc}" install

## Log
log_dependency "${dep_name}" "${deps_log_dir}" "${src_dir}" "${dep_ver}"
