FROM ubuntu:jammy

ENV DEBIAN_FRONTEND=noninteractive
ENV HOME=/home/illixr

RUN apt-get update && apt-get -y install cmake g++ extra-cmake-modules glslang-tools lsb-core libgl1 vulkan-tools libboost-all-dev libglew-dev libglu1-mesa-dev libsqlite3-dev libx11-dev libgl-dev pkg-config libopencv-dev libeigen3-dev libc6-dev libspdlog-dev git libglfw3-dev libglm-dev libjpeg-dev libusb-1.0.0-dev libuvc-dev libopencv-dev libopenxr-dev libopenxr1-monado libpng-dev libsdl2-dev libtiff-dev udev libudev-dev libvulkan-dev vulkan-validationlayers libwayland-dev wayland-protocols libx11-xcb-dev libxcb-glx0-dev libxcb-randr0-dev libxkbcommon-dev

# Create user directory
RUN mkdir -p /home/illixr && \
    chmod 777 /home/illixr

COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

ENTRYPOINT ["/entrypoint.sh"]