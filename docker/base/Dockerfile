ARG ILLIXR_VERSION="x"
ARG CUDA_MAJOR=12
ARG CUDA_MINOR=6
ARG CUDA_PATCH=3

ARG UBUNTU_RELEASE_YEAR=24

FROM nvidia/cuda:${CUDA_MAJOR}.${CUDA_MINOR}.${CUDA_PATCH}-cudnn-devel-ubuntu${UBUNTU_RELEASE_YEAR}.04
ENV DEBIAN_FRONTEND=noninteractive

#variables to store in image
ENV ILLIXR_VERSION=${ILLIXR_VERSION} \
    CUDA_VERSION="${CUDA_MAJOR}.${CUDA_MINOR}.${CUDA_PATCH}" \
    UBUNTU_RELEASE_YEAR=${UBUNTU_RELEASE_YEAR}

RUN apt-get update
RUN apt-get install -y curl apt-transport-https lsb-release software-properties-common sudo

RUN mkdir -p /etc/apt/keyrings
RUN add-apt-repository ppa:ecal/ecal-latest -y
RUN curl -sSf https://librealsense.intel.com/Debian/librealsense.pgp | sudo tee /etc/apt/keyrings/librealsense.pgp > /dev/null
RUN echo "deb [signed-by=/etc/apt/keyrings/librealsense.pgp] https://librealsense.intel.com/Debian/apt-repo `lsb_release -cs` main" | sudo tee /etc/apt/sources.list.d/librealsense.list

RUN apt-get update

RUN apt-get install -y build-essential cmake cmake-data g++ gcc git pkg-config protobuf-compiler libprotobuf-dev libprotoc-dev ccache libc6-dev 
RUN apt-get install -y libeigen3-dev libgflags-dev libgoogle-glog-dev libboost-all-dev 
RUN apt-get install -y csh tcsh dash bash bash-completion less unzip vim-tiny wget zip libcurl4-openssl-dev 
RUN apt-get install -y libx11-dev libx11-xcb-dev libxcb-glx0-dev libxcb-randr0-dev libxkbcommon-dev wayland-protocols libwayland-dev libxcb-icccm4-dev
RUN apt-get install -y libgl-dev libgles-dev libglew-dev libglfw3-dev libglib2.0-dev libglm-dev libglu1-mesa-dev 
RUN apt-get install -y doxygen doxygen-doxyparse doxygen-latex mkdocs mkdocs-autorefs mkdocs-bootstrap mkdocs-gen-files mkdocs-macros-plugin mkdocs-material mkdocs-material-extensions mkdocs-nature mkdocs-redirects mkdocs-section-index mkdocstrings 
RUN apt-get install -y libjpeg-dev libopencv-dev libtiff-dev libgtk-3-dev libgl1-mesa-dev libpng-dev libgstreamer-plugins-base1.0-dev libgstreamer1.0-dev 
RUN apt-get install -y libssl-dev libtool libudev-dev libusb-1.0.0-dev libuvc-dev libusb-1.0-0-dev ecal udev 
RUN apt-get install -y libopenxr-dev libopenxr1-monado 
RUN apt-get install -y libcpuinfo-dev libfmt-dev libfp16-dev libopenni2-dev libqcow-dev libsdl2-dev libspdlog-dev libsqlite3-dev at v4l-utils 


# realsense
RUN wget https://github.com/IntelRealSense/librealsense/raw/master/scripts/libuvc_installation.sh
RUN chmod +x ./libuvc_installation.sh
RUN ./libuvc_installation.sh
RUN rm libuvc_installation.sh

RUN apt-get install -y nvidia-cuda-toolkit-gcc

ENV NVIDIA_DRIVER_CAPABILITIES=graphics,compute,video,utility

RUN echo "Europe/Paris" > /etc/localtime ; echo "CUDA Version ${CUDA_MAJOR}.${CUDA_MINOR}.${CUDA_PATCH}" > /usr/local/cuda/version.txt

RUN apt-get upgrade -y
RUN apt-get install -y --no-install-recommends python3 libgomp1 python3-numpy python3-opencv python3-pip
RUN pip install --break-system-packages mkdoxy
RUN pip install --break-system-packages mkdocs-include-markdown-plugin
RUN pip install --break-system-packages mkdocs-glightbox


# clean up cache
RUN rm -rf /var/lib/apt/lists/*

# give user illixr passwordless sudo (required for ZED install)
RUN useradd -m -d /home/illixr illixr && adduser illixr sudo
RUN echo "illixr ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/illixr-user

# set up environment
#  have to force this otherwise CMake won't see it
ENV SHELL=/bin/bash
# docker doesn't set this for some reason
ENV USER=illixr

USER illixr
WORKDIR /home/illixr
