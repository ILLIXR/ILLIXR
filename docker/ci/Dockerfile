FROM ubuntu:jammy

ENV DEBIAN_FRONTEND=noninteractive
ENV HOME=/home/illixr

RUN apt-get update && apt-get -y install cmake g++ gdb extra-cmake-modules glslang-tools lsb-core libgl1 vulkan-tools libboost-all-dev libglew-dev libglu1-mesa-dev libsqlite3-dev libx11-dev libgl-dev pkg-config libopencv-dev libeigen3-dev libc6-dev libspdlog-dev git libglfw3-dev libglm-dev libjpeg-dev libusb-1.0.0-dev libuvc-dev libopencv-dev libopenxr-dev libopenxr1-monado libpng-dev libsdl2-dev libtiff-dev udev libudev-dev libvulkan-dev vulkan-validationlayers libwayland-dev wayland-protocols libx11-xcb-dev libxcb-glx0-dev libxcb-randr0-dev libxkbcommon-dev

RUN addgroup --gid 1000 illixr && \
    adduser --uid 1000 --ingroup illixr --home /home/illixr --shell /bin/sh --disabled-password --gecos "" illixr && \
    USER=illixr && \
    GROUP=illixr && \
    curl -SsL https://github.com/boxboat/fixuid/releases/download/v0.6.0/fixuid-0.6.0-linux-amd64.tar.gz | tar -C /usr/local/bin -xzf - && \
    chown root:root /usr/local/bin/fixuid && \
    chmod 4755 /usr/local/bin/fixuid && \
    mkdir -p /etc/fixuid && \
    printf "user: $USER\ngroup: $GROUP\n" > /etc/fixuid/config.yml

COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

USER illixr:illixr
ENTRYPOINT ["/entrypoint.sh"]