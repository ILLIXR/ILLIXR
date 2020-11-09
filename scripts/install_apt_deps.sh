#!/bin/bash


### Initial setup ###

# Source environment variables describing the current OS distribution
. /etc/os-release


### Package dependencies setup ###

# List of common package dependencies for the ILLIXR project
pkg_dep_list_common="
    build-essential
    git
    clang-10
    make
    cmake
    unzip
    wget
    xvfb
    pkg-config
    libc++-dev libc++abi-dev
    libsqlite3-dev
    libeigen3-dev
    libboost-all-dev
    libatlas-base-dev
    libsuitesparse-dev
    libblas-dev
    libtbb-dev
    libsdl2-dev
    libglu1-mesa-dev mesa-common-dev
    libglew-dev glew-utils libglfw3-dev freeglut3-dev glslang-tools
    libusb-dev libusb-1.0 libudev-dev libv4l-dev libhidapi-dev
    libx11-xcb-dev libx11-dev libxcb-glx0-dev libxcb-randr0-dev libxrandr-dev
    libxkbcommon-dev
    libwayland-dev
    libepoxy-dev
    libdrm-dev
    libgbm-dev
    libvirglrenderer-dev
    libpulse-dev
    libgtest-dev
    libgtk2.0-dev
    libjpeg-dev libpng-dev libtiff-dev libvtk6-dev
    libgtk-3-dev
    libparmetis-dev
    libgflags-dev libgoogle-glog-dev gfortran
" # End list

# Flatten the package dependency list string to a single line
pkg_dep_list="$(echo ${pkg_dep_list_common} | xargs) "


### IntelRealSense package dependencies setup ###

pkg_dep_list_realsense="librealsense2-dkms librealsense2-utils"
pkg_install_url_realsense="https://github.com/IntelRealSense/librealsense/blob/master/doc/distribution_linux.md"
pkg_build_url_realsense="https://github.com/IntelRealSense/librealsense/blob/master/doc/installation.md"
pkg_warn_msg_realsense="Currently, IntelRealSense does not support binary package installations for Ubuntu 20 LTS kernels, or other non-Ubuntu Linux distributions. If your project requires IntelRealSense support, please build and install the IntelRealSense SDK from source. For more information, visit '${pkg_install_url_realsense}' and '${pkg_build_url_realsense}'."

# Check for distribution support of IntelRealSense
# For supported distributions, automatically add the required install
# package dependencies
echo "Detected OS: '${PRETTY_NAME}'"
if [ "${ID}" == "ubuntu" ] && [ "${VERSION_ID}" == "18.04" ]; then
    use_realsense="yes"
    pkg_dep_list+="${pkg_dep_list_realsense} "
elif [ "${ID}" == "ubuntu" ] && [ "${VERSION_ID}" == "20.04" ]; then
    use_realsense="no"
    print_warning "${pkg_warn_msg_realsense}"
else
    use_realsense="no"
    print_warning "${pkg_warn_msg_realsense}"
fi


### Package dependencies installation ###

sudo apt install -y software-properties-common curl gnupg2
curl https://apt.kitware.com/keys/kitware-archive-latest.asc | sudo apt-key add -
sudo apt-add-repository -u -y "deb https://apt.kitware.com/ubuntu/ ${UBUNTU_CODENAME} main"

# If supported, add the gpg keys and repository for IntelRealSense
if [ "${use_realsense}" == "yes" ]; then
    sudo apt-key adv --keyserver keys.gnupg.net --recv-key F6E65AC044F831AC80A06380C8B3A55A6F3EFCDE || sudo apt-key adv --keyserver hkp://keyserver.ubuntu.com:80 --recv-key F6E65AC044F831AC80A06380C8B3A55A6F3EFCDE
    sudo add-apt-repository "deb http://realsense-hw-public.s3.amazonaws.com/Debian/apt-repo bionic main" -u
fi

sudo add-apt-repository -u -y ppa:graphics-drivers/ppa
sudo add-apt-repository -u -y ppa:deadsnakes/ppa
sudo apt-get install -y ${pkg_dep_list}
