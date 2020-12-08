#!/bin/bash

#--- Script for installing GTSAM ---#


### Default imported variables setup ###

if [ -z "${opt_dir}" ]; then
    opt_dir="/opt/ILLIXR"
fi

if [ -z "${illixr_nproc}" ]; then
    illixr_nproc="1"
fi


### Package metadata setup ###

branch_tag_name="kimera-gtsam"
repo_url="https://github.com/ILLIXR/gtsam.git"
gtsam_dir="${opt_dir}/gtsam"
prefix_dir="/usr/local"
build_type="RelWithDebInfo"
so_file="libgtsam${build_type}.so"


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
cd "${gtsam_dir}/build/gtsam"
if [ -f "${so_file}" ]; then
    sudo ln -s "${so_file}" libgtsam.so
fi
cd -
