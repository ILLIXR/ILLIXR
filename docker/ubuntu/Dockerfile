ARG ILLIXR_VERSION="v4.0"
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

RUN apt-get install -y build-essential cmake cmake-data csh tcsh dash bash bash-completion g++ gcc git less sudo unzip vim-tiny wget zip pkg-config protobuf-compiler libprotobuf-dev libprotoc-dev libeigen3-dev libgflags-dev libgoogle-glog-dev
RUN apt-get install -y libboost-all-dev doxygen doxygen-doxyparse doxygen-latex mkdocs mkdocs-autorefs mkdocs-bootstrap mkdocs-gen-files mkdocs-macros-plugin mkdocs-material mkdocs-material-extensions mkdocs-nature mkdocs-redirects mkdocs-section-index mkdocstrings ecal udev libc6-dev libcpuinfo-dev libcurl4-openssl-dev libfmt-dev libfp16-dev libgl-dev libgles-dev libglew-dev libglfw3-dev libglib2.0-dev libglm-dev libglu1-mesa-dev libgstreamer-plugins-base1.0-dev libgstreamer1.0-dev libjpeg-dev libopencv-dev libopenni2-dev libopenxr-dev libopenxr1-monado libpng-dev libqcow-dev libsdl2-dev libspdlog-dev libsqlite3-dev libssl-dev libtiff-dev libtool libudev-dev libusb-1.0.0-dev libuvc-dev libwayland-dev libx11-dev libx11-xcb-dev libxcb-glx0-dev libxcb-randr0-dev libxkbcommon-dev wayland-protocols ccache librealsense2-dev librealsense2-gl-dev
RUN apt-get install -y nvidia-cuda-toolkit-gcc

ENV NVIDIA_DRIVER_CAPABILITIES=graphics,compute,video,utility

RUN echo "Europe/Paris" > /etc/localtime ; echo "CUDA Version ${CUDA_MAJOR}.${CUDA_MINOR}.${CUDA_PATCH}" > /usr/local/cuda/version.txt

RUN apt-get upgrade -y
RUN apt-get install -y --no-install-recommends python3 libgomp1 python3-numpy python3-opencv

RUN apt-get install -y cuda-nvcc-${CUDA_MAJOR}-${CUDA_MINOR} fontconfig libgnutls28-dev ladspa-sdk libaom-dev libass-dev libbluray-dev libbs2b-dev libdav1d-dev libdrm-dev libfreetype-dev libfribidi-dev libgsm1-dev libiec61883-dev libjack-dev libmodplug-dev libmp3lame-dev libopenjp2-7-dev libopenmpt-dev libopus-dev libpulse-dev librsvg2-dev libssh-dev libv4l-dev libvorbis-dev libvpl-dev libvpx-dev libwebp-dev libxml2-dev libzimg-dev ocl-icd-opencl-dev libxcb-shm0-dev libsoxr-dev libspeex-dev libtheora-dev libx264-dev libx265-dev libgmp-dev yasm cuda-cudart-dev-${CUDA_MAJOR}-${CUDA_MINOR} libxcb1-dev libopengl-dev portaudio19-dev libspatialaudio-dev

RUN apt-get install -y libzstd-dev
RUN apt-get install -y zstd

RUN groupadd -U illixr zed
USER illixr
WORKDIR /home/illixr

# ZED sdk install
RUN wget -O ZED_SDK_Linux_Ubuntu${UBUNTU_RELEASE_YEAR}.run https://download.stereolabs.com/zedsdk/${ZED_SDK_MAJOR}.${ZED_SDK_MINOR}/cu${CUDA_MAJOR}/ubuntu${UBUNTU_RELEASE_YEAR}
RUN chmod +x ZED_SDK_Linux_Ubuntu${UBUNTU_RELEASE_YEAR}.run
RUN ./ZED_SDK_Linux_Ubuntu${UBUNTU_RELEASE_YEAR}.run --accept -- silent skip_python skip_cuda
RUN rm ZED_SDK_Linux_Ubuntu${UBUNTU_RELEASE_YEAR}.run
RUN cd /usr/local/zed/resources
RUN cd /usr/local/zed/lib/

# clean up cache
RUN rm -rf /var/lib/apt/lists/*

# give user illixr passwordless sudo (required for ZED install)
RUN useradd -m -d /home/illixr illixr && adduser illixr sudo
RUN echo "illixr ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/illixr-user

# set up environment
#  have to force this otherwise CMake won't see it
ENV SHELL=/bin/sh
# docker doesn't set this for some reason
ENV USER=illixr

USER illixr
WORKDIR /home/illixr
