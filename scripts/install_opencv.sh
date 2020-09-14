#!/bin/bash

git clone --branch 3.4.6 https://github.com/opencv/opencv/ "${temp_dir}/opencv"
git clone --branch 3.4.6 https://github.com/opencv/opencv_contrib/  "${temp_dir}/opencv_contrib"
cmake \
	-S "${temp_dir}/opencv" \
	-B "${temp_dir}/opencv/build" \
	-D CMAKE_BUILD_TYPE=Release \
	-D CMAKE_INSTALL_PREFIX=/usr/local \
	-D BUILD_TESTS=OFF \
	-D BUILD_PERF_TESTS=OFF \
	-D BUILD_EXAMPLES=OFF \
	-D BUILD_JAVA=OFF \
	-D WITH_OPENGL=ON \
	-D OPENCV_EXTRA_MODULES_PATH="${temp_dir}/opencv_contrib/modules"
sudo make -C "${temp_dir}/opencv/build" "-j$(nproc)" install
sudo ldconfig -v
