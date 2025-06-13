ARG PARENT_IMAGE="x"
ARG ILLIXR_VERSION="x"

FROM ${PARENT_IMAGE}:${ILLIXR_VERSION}

ARG CUDA_MAJOR=12
ARG CUDA_MINOR=6

USER root
RUN apt-get update
RUN apt-get install -y cuda-nvcc-${CUDA_MAJOR}-${CUDA_MINOR} fontconfig libgnutls28-dev ladspa-sdk libaom-dev libass-dev libbluray-dev libbs2b-dev libdav1d-dev libdrm-dev libfreetype-dev libfribidi-dev libgsm1-dev libiec61883-dev libjack-dev libmodplug-dev libmp3lame-dev libopenjp2-7-dev libopenmpt-dev libopus-dev libpulse-dev librsvg2-dev libssh-dev libv4l-dev libvorbis-dev libvpl-dev libvpx-dev libwebp-dev libxml2-dev libzimg-dev ocl-icd-opencl-dev libxcb-shm0-dev libsoxr-dev libspeex-dev libtheora-dev libx264-dev libx265-dev libgmp-dev yasm cuda-cudart-dev-${CUDA_MAJOR}-${CUDA_MINOR} libxcb1-dev libopengl-dev

USER illixr
ENV USE_GPL=1
