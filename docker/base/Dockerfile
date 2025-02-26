ARG ILLIXR_VERSION="latest"
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
RUN apt-get -y install curl apt-transport-https lsb-release software-properties-common

RUN mkdir -p /etc/apt/keyrings
RUN add-apt-repository ppa:ecal/ecal-latest -y
RUN apt-get update

RUN apt-get install -y build-essential cmake csh tcsh dash bash bash-completion g++ gcc git less sudo unzip vim-tiny wget zip patch pkg-config protobuf-compiler libprotobuf-dev libprotoc-dev libeigen3-dev libgflags-dev libgoogle-glog-dev
RUN apt-get install -y libboost-all-dev doxygen doxygen-doxyparse doxygen-latex mkdocs mkdocs-autorefs mkdocs-bootstrap mkdocs-gen-files mkdocs-macros-plugin mkdocs-material mkdocs-material-extensions mkdocs-nature mkdocs-redirects mkdocs-section-index mkdocstrings ecal udev libc6-dev libcpuinfo-dev libcurl4-openssl-dev libfmt-dev libfp16-dev libgl-dev libgles-dev libglew-dev libglfw3-dev libglib2.0-dev libglm-dev libglu1-mesa-dev libgstreamer-plugins-base1.0-dev libgstreamer1.0-dev libjpeg-dev libopencv-dev libopenni2-dev libopenxr-dev libopenxr1-monado libpng-dev libqcow-dev libsdl2-dev libspdlog-dev libsqlite3-dev libssl-dev libtiff-dev libtool libudev-dev libusb-1.0.0-dev libuvc-dev libvulkan-dev libwayland-dev libx11-dev libx11-xcb-dev libxcb-glx0-dev libxcb-randr0-dev libxkbcommon-dev wayland-protocols ccache
RUN apt-get install -y nvidia-cuda-toolkit-gcc vulkan-validationlayers mesa-vulkan-drivers glslang-dev glslang-tools

ENV NVIDIA_DRIVER_CAPABILITIES=graphics,compute,video,utility

RUN echo "Europe/Paris" > /etc/localtime ; echo "CUDA Version ${CUDA_MAJOR}.${CUDA_MINOR}.${CUDA_PATCH}" > /usr/local/cuda/version.txt

RUN apt-get upgrade -y
RUN apt-get install -y --no-install-recommends less python3 libgomp1 python3-numpy python3-opencv

# install realsense libraries
RUN git clone --depth=1 --branch v2.56.3 https://github.com/IntelRealSense/librealsense.git /tmp/librealsense
RUN mkdir -p /tmp/librealsense/build
WORKDIR /tmp/librealsense/build
RUN cmake ..
RUN make -j4 && make install
WORKDIR /
RUN rm -rf librealsense

# clean up cache
RUN rm -rf /var/lib/apt/lists/*

# give user illixr passwordless sudo (required for ZED install)
RUN useradd -m -d /home/illixr illixr && adduser illixr sudo
RUN echo "illixr ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/illixr-user
USER illixr
WORKDIR /home/illixr
