#!/bin/bash

#--- Script for installing OpenXR SDK ---#


### Setup ###

## Source the default values for imported variables
. scripts/default_values.sh

## Source the global helper functions
. scripts/system_utils.sh

## Source the configurations for our dependencies
. deps.sh


### Package metadata setup ###

if [ -z "${dep_name}" ]; then
    dep_name="${dep_name_qemu}"
fi

if [ -z "${src_dir}" ]; then
    src_dir="${parent_dir_qemu}/${dep_name_qemu}"
fi

if [ -z "${dep_ver}" ]; then
    dep_ver="${dep_ver_qemu}"
fi

repo_url="git://git.qemu.org/qemu.git"
build_dir="${src_dir}/build"


### Fetch, build and install ###

## Fetch
git clone --recursive --depth 1 --branch "${dep_ver}" "${repo_url}" "${src_dir}"
mkdir -p "${build_dir}"
cd "${build_dir}"

## Build
../configure \
    --enable-sdl \
    --enable-opengl \
    --enable-virglrenderer \
    --enable-system \
    --enable-modules \
    --audio-drv-list=pa \
    --target-list=x86_64-softmmu \
    --enable-kvm \
    #--prefix="${prefix_dir}" \ ## For installing. See below.
    CC="${CC}" \
    CXX="${CXX}"
make -j "${illixr_nproc}"

## Install
## Not installing yet, as the previous version of this script did not.
#sudo make -j "${illixr_nproc}" install

## Go back
cd -

### Qemu is located at:
### ${build_dir}/x86_64-softmmu/qemu-system-x86_64

## Log
log_dependency "${dep_name}" "${deps_log_dir}" "${src_dir}" "${dep_ver}"
