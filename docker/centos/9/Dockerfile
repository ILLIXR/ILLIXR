FROM quay.io/centos/centos:stream9
LABEL maintainer='Douglas N Friedel <friedel@illinois.edu>'

USER root
RUN dnf -y install dnf-plugins-core epel-release
RUN dnf config-manager --set-enabled crb
RUN dnf -y install glew-devel mesa-libGLU-devel sqlite-devel libX11-devel mesa-libGL-devel git boost-devel eigen3-devel glslang-devel gflags-devel libjpeg-turbo-devel libpng-devel libtiff-devel systemd systemd-devel wayland-devel wayland-protocols-devel libX11-xcb libxcb-devel libxkbcommon-devel openxr-devel vulkan-loader-devel SDL2-devel cmake gcc-c++ extra-cmake-modules glfw-devel pkgconf-pkg-config libusb1-devel patch fmt-devel glog-devel openssl-devel vtk-devel SDL2-devel protobuf-devel protobuf-compiler graphviz hdf5-devel libcurl-devel qwt-qt5-devel qt5-qtbase-devel libyaml-devel zlib-devel openssl-devel librealsense-devel
RUN sed -i 's/^[ \t]*//g' /usr/lib64/pkgconfig/*
RUN ln -s /usr/include /include
RUN useradd -ms /bin/bash illixr

USER illixr
WORKDIR /home/illixr
RUN git clone -b convertBuildToCmake https://github.com/ILLIXR/ILLIXR.git
COPY --chown=illixr cmk.sh .
RUN chmod 744 cmk.sh
