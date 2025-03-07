FROM ubuntu:focal
LABEL maintainer='Douglas N Friedel <friedel@illinois.edu>'

USER root
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update
RUN apt-get -y install curl apt-transport-https lsb-core

RUN mkdir -p /etc/apt/keyrings
RUN curl -sSf https://librealsense.intel.com/Debian/librealsense.pgp | tee /etc/apt/keyrings/librealsense.pgp > /dev/null
RUN echo "deb [signed-by=/etc/apt/keyrings/librealsense.pgp] https://librealsense.intel.com/Debian/apt-repo `lsb_release -cs` main" | tee /etc/apt/sources.list.d/librealsense.list > /dev/null
RUN more /etc/apt/sources.list.d/librealsense.list
RUN apt-get update
RUN apt-get -y install wget software-properties-common

RUN apt-get update

RUN apt-get -y install libglew-dev libglu1-mesa-dev libsqlite3-dev libx11-dev libgl1-mesa-dev git libboost-all-dev libeigen3-dev glslang-dev glslang-tools libgflags-dev libjpeg-dev libpng-dev libtiff-dev udev libudev-dev libwayland-dev wayland-protocols libx11-xcb-dev libxcb-glx0-dev libxcb-randr0-dev libxkbcommon-dev libvulkan-dev libsdl2-dev cmake g++ extra-cmake-modules libopencv-dev libglfw3-dev pkg-config libusb-1.0.0-dev patch libfmt-dev libgoogle-glog-dev libssl-dev libsdl2-dev libprotobuf-dev libprotoc-dev protobuf-compiler graphviz zlib1g-dev qtbase5-dev libhdf5-dev libcurl4-openssl-dev libqwt-qt5-dev libyaml-cpp-dev librealsense2-dev librealsense2-gl-dev

RUN mkdir -p /tmp_build
WORKDIR /tmp_build
RUN git clone https://github.com/KhronosGroup/OpenXR-SDK.git
WORKDIR /tmp_build/OpenXR-SDK
RUN mkdir -p build
WORKDIR /tmp_build/OpenXR-SDK/build
RUN cmake ..
RUN make -j4
RUN make install
WORKDIR /
RUN rm -rf /tmp_build
RUN useradd -ms /bin/bash illixr

USER illixr
WORKDIR /home/illixr
RUN git clone -b convertBuildToCmake https://github.com/ILLIXR/ILLIXR.git
COPY --chown=illixr cmk.sh .
RUN chmod 744 cmk.sh
