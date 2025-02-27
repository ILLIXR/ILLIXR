FROM ubuntu:jammy

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get -y install curl apt-transport-https lsb-core libgl1 vulkan-tools libglew-dev libglu1-mesa-dev libsqlite3-dev libx11-dev libgl1-mesa-dev git libboost-all-dev libeigen3-dev glslang-dev glslang-tools libgflags-dev libjpeg-dev libpng-dev libtiff-dev udev libudev-dev libwayland-dev wayland-protocols libx11-xcb-dev libxcb-glx0-dev libxcb-randr0-dev libxkbcommon-dev libopenxr-dev libopenxr1-monado libvulkan-dev libsdl2-dev cmake g++ extra-cmake-modules libopencv-dev libglfw3-dev pkg-config libusb-1.0.0-dev patch libfmt-dev libgoogle-glog-dev libssl-dev libsdl2-dev libprotobuf-dev libprotoc-dev protobuf-compiler graphviz zlib1g-dev qtbase5-dev libhdf5-dev libcurl4-openssl-dev libqwt-qt5-dev libyaml-cpp-dev librealsense2-dev librealsense2-gl-dev

COPY entrypoint.sh /entrypoint.sh

ENTRYPOINT ["/entrypoint.sh"]