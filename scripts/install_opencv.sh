#!/bin/bash

git clone --branch 3.4.6-instrumented https://github.com/ILLIXR/opencv "${temp_dir}/opencv"
git clone --branch 3.4.6 https://github.com/opencv/opencv_contrib/  "${temp_dir}/opencv_contrib"
if ! nvcc --version &> /dev/null
then
    echo "nvcc not found"
else
    echo "nvcc found"
    extra_cmake_args="
	    -D WITH_CUDA=ON
	    -D WITH_CUBLAS=1
	    -D CUDA_NVCC_FLAGS=-allow-unsupported-compiler
	    -D BUILD_opencv_cudacodec=OFF"
fi
cmake \
	    -S "${temp_dir}/opencv" \
	    -B "${temp_dir}/opencv/build" \
	    -D CMAKE_BUILD_TYPE=RelWithDebInfo \
	    -D CMAKE_INSTALL_PREFIX=/usr/local \
	    -D BUILD_TESTS=OFF \
	    -D BUILD_PERF_TESTS=OFF \
	    -D BUILD_EXAMPLES=OFF \
	    -D BUILD_JAVA=OFF \
	    -D WITH_OPENGL=ON \
	    -D WITH_VTK=ON \
	    -D OPENCV_EXTRA_MODULES_PATH="${temp_dir}/opencv_contrib/modules" \
	    ${extra_cmake_args}
sudo make -C "${temp_dir}/opencv/build" "-j${illixr_nproc}" install

# Remove temp cmake files
rm -f a.out cmake_hdf5_test.o
