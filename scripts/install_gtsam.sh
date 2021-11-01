#!/bin/bash

#--- Script for installing GTSAM ---#


### Setup ###

## Source the default values for imported variables
. scripts/default_values.sh

## Source the global helper functions
. scripts/system_utils.sh

## Source the configurations for our dependencies
. deps.sh


### Package metadata setup ###

if [ -z "${dep_name}" ]; then
    dep_name="${dep_name_gtsam}"
fi

if [ -z "${src_dir}" ]; then
    src_dir="${parent_dir_gtsam}/${dep_name_gtsam}"
fi

if [ -z "${dep_ver}" ]; then
    dep_ver="${dep_ver_gtsam}"
fi

repo_url="https://github.com/ILLIXR/gtsam.git"
build_dir="${src_dir}/build"

case "${build_type}" in
    Release)        so_file="libgtsam.so"
                    so_file_unstable="libgtsam_unstable.so"
                    ;;
    RelWithDebInfo) so_file="libgtsamRelWithDebInfo.so"
                    so_file_unstable="libgtsam_unstableRelWithDebInfo.so"
                    ;;
    Debug)          so_file="libgtsamDebug.so"
                    so_file_unstable="libgtsam_unstableDebug.so"
                    ;;
    *)              print_warning "Bad cmake build type '${build_type}'"
                    exit 1
                    ;;
esac


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
    -D CMAKE_INSTALL_PREFIX="${prefix_dir}" \
    -D GTSAM_WITH_TBB=OFF \
    -D GTSAM_USE_SYSTEM_EIGEN=ON \
    -D GTSAM_POSE3_EXPMAP=ON \
    -D GTSAM_ROT3_EXPMAP=ON \
    -D GTSAM_WITH_EIGEN_UNSUPPORTED=ON
make -C "${build_dir}" -j "${illixr_nproc}"

## Install
# Fix suffixed symlinks for the generated shared libaries
if [ "${build_type}" != "Release" ]; then
    so_file_release="libgtsam.so"
    so_file_unstable_release="libgtsam_unstable.so"

    cd "${build_dir}/gtsam"

    if  [ -f "${so_file}" ]; then
        if [ -f "${so_file_release}" ]; then
            sudo rm -f --preserve-root=all "${so_file_release}"
        fi
        sudo ln -s "${so_file}" "${so_file_release}"
    fi

    if  [ -f "${so_file_unstable}" ]; then
        if [ -f "${so_file_unstable_release}" ]; then
            sudo rm -f --preserve-root=all "${so_file_unstable_release}"
        fi
        sudo ln -s "${so_file_unstable}" "${so_file_unstable_release}"
    fi

    cd -
fi
sudo make -C "${build_dir}" -j "${illixr_nproc}" install
sudo ln -s /usr/local/lib/${so_file} /usr/local/lib/libgtsam.so 
## Log
log_dependency "${dep_name}" "${deps_log_dir}" "${src_dir}" "${dep_ver}"
