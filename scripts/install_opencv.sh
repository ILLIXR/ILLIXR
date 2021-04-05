#!/bin/bash

#--- Script for installing OpenCV ---#


## Source the default values for imported variables
. scripts/default_values.sh

## Source the global helper functions
. scripts/system_utils.sh

## Source the configurations for our dependencies
. deps.sh


### Package metadata setup ###

if [ -z "${dep_name}" ]; then
    dep_name="${dep_name_opencv}"
fi

if [ -z "${src_dir}" ]; then
    src_dir="${parent_dir_opencv}/${dep_name_opencv}"
fi

if [ -z "${dep_ver}" ]; then
    dep_ver="${dep_ver_opencv}"
fi

dep_ver_extra="3.4.6"
repo_url="https://github.com/ILLIXR/opencv"
repo_url_extra="https://github.com/opencv/opencv_contrib"
opencv_dir_extra="${opt_dir}/opencv_contrib"
build_dir="${src_dir}/build"
file_cleanup_list="a.out cmake_hdf5_test.o"

flags_cuda="-allow-unsupported-compiler"
# -gencode arch=compute_${target_arch_ver_cuda},code=sm_${target_arch_ver_cuda}"


### Checks ###

## Append extra cmake arguments if CUDA support is detected
check_compiler_cuda=$(nvcc -V 2>/dev/null)
if [ "$?" -eq 0 ]; then
    echo "nvcc found: Compiling OpenCV with CUDA"
    extra_cmake_args="
        -D WITH_CUDA=ON
        -D WITH_CUBLAS=1
        -D CUDA_NVCC_FLAGS='${flags_cuda}'
        -D BUILD_opencv_cudacodec=OFF"
else
    echo "nvcc not found: Compiling OpenCV without CUDA"
    extra_cmake_args=""
fi


### Fetch, build and install ###

## Fetch
git clone --depth 1 --branch "${dep_ver}" "${repo_url}" "${src_dir}"
git clone --depth 1 --branch "${dep_ver_extra}" "${repo_url_extra}" "${opencv_dir_extra}"

## Build
cmake \
    -S "${src_dir}" \
    -B "${build_dir}" \
    -D CMAKE_BUILD_TYPE="${build_type}" \
    -D CMAKE_INSTALL_PREFIX="${prefix_dir}" \
    -D BUILD_TESTS=OFF \
    -D BUILD_PERF_TESTS=OFF \
    -D BUILD_EXAMPLES=OFF \
    -D BUILD_JAVA=OFF \
    -D WITH_OPENGL=ON \
    -D WITH_VTK=ON \
    -D OPENCV_EXTRA_MODULES_PATH="${opencv_dir_extra}/modules" \
    ${extra_cmake_args}
make -C "${build_dir}" -j "${illixr_nproc}"

## Install
sudo make -C "${build_dir}" -j "${illixr_nproc}" install

## Remove temp cmake files
rm -f ${file_cleanup_list}

## Log
log_dependency "${dep_name}" "${deps_log_dir}" "${src_dir}" "${dep_ver}"
