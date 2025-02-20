ARG PARENT_IMAGE="illixr/base"
ARG ILLIXR_VERSION="3.3"

FROM ${PARENT_IMAGE}:${ILLIXR_VERSION}

USER root

RUN apt-get install -y cuda-llvm fontconfig gnutls ladspa libaom libass libbluray libbs2b libdav1d libdrm libfreetype libfribidi libgsm libiec61883 libjack libmodplug libmp3lame libopenjpeg libopenmpt libopus libpulse librsvg libssh libv4l2 libvorbis libvpl libvpx libwebp libxml2 libzimg opencl

USER illixr
