#!/bin/bash

#--- Script for installing GTSAM ---#


### Default imported variables setup ###

if [ -z "${opt_dir}" ]; then
    opt_dir="/opt/ILLIXR"
fi

if [ -z "${illixr_nproc}" ]; then
    illixr_nproc="1"
fi


### Helper functions ###

# Source the global helper functions
. scripts/bash_utils.sh


### Package metadata setup ###

branch_tag_name="kimera-gtsam"
repo_url="https://github.com/ILLIXR/gtsam.git"
gtsam_dir="${opt_dir}/gtsam"
prefix_dir="/usr/local"
build_type="RelWithDebInfo"

case "${build_type}" in
    Release)          so_file="libgtsam.so" ;;
    RelWithDebInfo)   so_file="libgtsamRelWithDebInfo.so" ;;
    Debug)            so_file="libgtsamDebug.so" ;;
    *)                print_warning "Bad cmake build type '${build_type}'" && exit 1 ;;
esac


### Fetch, build and install ###

# Fetch
git clone --branch "${branch_tag_name}" "${repo_url}" "${gtsam_dir}"

# Build
cmake \
	-S "${gtsam_dir}" \
	-B "${gtsam_dir}/build" \
	-D CMAKE_BUILD_TYPE="${build_type}" \
	-D CMAKE_INSTALL_PREFIX="${prefix_dir}" \
	-D GTSAM_WITH_TBB=OFF \
	-D GTSAM_USE_SYSTEM_EIGEN=OFF \
	-D GTSAM_POSE3_EXPMAP=ON \
	-D GTSAM_ROT3_EXPMAP=ON

# Install
sudo make -C "${gtsam_dir}/build" "-j${illixr_nproc}" install

# Fix 'RelWithDebugInfo'-suffixed symlinks for the generated shared libaries
if [ "${build_type}" != "Release" ]; then
    cd "${gtsam_dir}/build/gtsam"
    if  [ -f "${so_file}" ]; then
        if [ -f libgtsam.so ]; then
            sudo rm -f libgtsam.so
        fi
        sudo ln -s "${so_file}" libgtsam.so
    fi
    cd -
fi
