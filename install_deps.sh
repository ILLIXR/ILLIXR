#!/bin/sh

set -x -e

. /etc/os-release

if [ "${ID_LIKE}" = debian -o "${ID}" = debian ]
then
    sudo add-apt-repository ppa:graphics-drivers/ppa
    wget -qO - https://apt.kitware.com/keys/kitware-archive-latest.asc | sudo apt-key add -
    sudo apt-add-repository 'deb https://apt.kitware.com/ubuntu/ bionic main'
    sudo apt-get update
    sudo apt-get install -y \
         git clang cmake libc++-dev libc++abi-dev \
         libeigen3-dev libboost-all-dev libatlas-base-dev libsuitesparse-dev libblas-dev \
         glslang-tools libsdl2-dev libglu1-mesa-dev mesa-common-dev freeglut3-dev libglew-dev glew-utils libglfw3-dev \
         libusb-dev libusb-1.0 libudev-dev libv4l-dev libhidapi-dev \
         build-essential libx11-xcb-dev libxkbcommon-dev libwayland-dev libxrandr-dev \
         libgtest-dev

    old_pwd="${PWD}"
    mkdir -p /tmp/ILLIXR_deps
    cd /tmp/ILLIXR_deps

    if [ ! -d opencv ]; then
        git clone --branch 3.4.6 https://github.com/opencv/opencv/
        git clone --branch 3.4.6 https://github.com/opencv/opencv_contrib/
        mkdir -p opencv/build && cd opencv/build
        cmake -D CMAKE_BUILD_TYPE=Release -D CMAKE_INSTALL_PREFIX=/usr/local -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_JAVA=OFF -DWITH_OPENGL=ON -DOPENCV_EXTRA_MODULES_PATH=../opencv_contrib/modules ..
        sudo make -j$(nproc) install
       cd ../..
    fi

    # if [ ! -d Vulkan-Headers ]; then
    #    git clone https://github.com/KhronosGroup/Vulkan-Headers.git
    #    mkdir -p Vulkan-Headers/build && cd Vulkan-Headers/build
    #    cmake -DCMAKE_INSTALL_PREFIX=install ..
    #    sudo make -j$(nproc) install
    #    cd ../..
    # fi

    # if [ ! -d Vulkan-Loader ]; then
    #    git clone https://github.com/KhronosGroup/Vulkan-Loader.git
    #    mkdir -p Vulkan-Headers/build && cd Vulkan-Headers/build
	#    ../scripts/update_deps.py
	#    cmake -C helper.cmake ..
	#    cmake --build .
    #    cd ../..
    # fi

    if [ ! -d OpenXR-SDK ]; then
        git clone https://github.com/KhronosGroup/OpenXR-SDK.git
        mkdir -p OpenXR-SDK/build && cd OpenXR-SDK/build;
        cmake ..
        sudo make -j$(nproc) install
        cd ../..
    fi

    cd "${old_pwd}"
else
    echo "${0} does not support ${ID_LIKE} yet."
    exit 1
fi
