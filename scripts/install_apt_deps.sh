#!/bin/bash

. /etc/os-release

sudo apt install -y software-properties-common curl gnupg2
curl https://apt.kitware.com/keys/kitware-archive-latest.asc | sudo apt-key add -
sudo apt-add-repository -u -y "deb https://apt.kitware.com/ubuntu/ ${UBUNTU_CODENAME} main"
sudo apt-key adv --keyserver keys.gnupg.net --recv-key F6E65AC044F831AC80A06380C8B3A55A6F3EFCDE || sudo apt-key adv --keyserver hkp://keyserver.ubuntu.com:80 --recv-key F6E65AC044F831AC80A06380C8B3A55A6F3EFCDE
sudo add-apt-repository "deb http://realsense-hw-public.s3.amazonaws.com/Debian/apt-repo bionic main" -u
sudo add-apt-repository -u -y ppa:graphics-drivers/ppa
sudo add-apt-repository -u -y ppa:deadsnakes/ppa
sudo apt-get install -y \
	 git clang-10 make cmake libc++-dev libc++abi-dev unzip \
	 libsqlite3-dev libeigen3-dev libboost-all-dev libatlas-base-dev libsuitesparse-dev libblas-dev libtbb-dev \
	 glslang-tools libsdl2-dev libglu1-mesa-dev mesa-common-dev freeglut3-dev libglew-dev glew-utils libglfw3-dev \
	 libusb-dev libusb-1.0 libudev-dev libv4l-dev libhidapi-dev \
	 build-essential libx11-xcb-dev libxcb-glx0-dev libxcb-randr0-dev libxrandr-dev libxkbcommon-dev libwayland-dev \
	 libepoxy-dev libdrm-dev libgbm-dev libx11-dev libvirglrenderer-dev libpulse-dev \
	 libgtest-dev pkg-config libgtk2.0-dev wget xvfb librealsense2-dkms librealsense2-utils \
     libjpeg-dev libpng-dev libtiff-dev libvtk6-dev libgtk-3-dev libparmetis-dev gfortran \
	 libgflags-dev libgoogle-glog-dev \
