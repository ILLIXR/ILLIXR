#!/bin/bash

#--- Script for installing Clang ---#


### Setup ###

## Source the default values for imported variables
. scripts/default_values.sh

## Source the global helper functions
. scripts/system_utils.sh

## Source the configurations for our dependencies
. deps.sh


### Package metadata setup ###

if [ -z "${dep_name}" ]; then
    dep_name="${dep_name_boost}"
fi

if [ -z "${src_dir}" ]; then
    src_dir="${parent_dir_boost}/${dep_name_boost}"
fi

if [ -z "${dep_ver}" ]; then
    dep_ver="${dep_ver_boost}"
fi

repo_url="https://github.com/boostorg/boost.git"
build_dir="${src_dir}/build"

case "${build_type}" in
    Release)        build_variant="release"
                    ;;
    RelWithDebInfo) build_variant="release" # This is a large build
                    ;;
    Debug)          build_variant="debug"
                    ;;
esac


### Checks ###

## Assert no system packages will be overwritten by this install
## If present, remove the conflicting packages before proceeding
pkg_list_boost="libboost-all-dev"
detect_packages "${pkg_list_boost}" "${PKG_MODE_FOUND_NONFATAL}"


### Fetch, build and install ###

## Fetch
git clone --depth 1 --branch "${dep_ver}" "${repo_url}" "${src_dir}"
cd "${src_dir}"
git submodule update --init

## Build
mkdir -p "${build_dir}"
"${src_dir}/bootstrap.sh" --prefix="${prefix_dir}"
"${src_dir}/b2" --build-dir="${build_dir}" -j "${illixr_nproc}" variant="${build_variant}"

## Install
sudo "${src_dir}/b2" --build-dir="${build_dir}" -j "${illixr_nproc}" install
cd -

## Log
log_dependency "${dep_name}" "${deps_log_dir}" "${src_dir}" "${dep_ver}"
