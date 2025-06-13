ARG PARENT_IMAGE="x"
ARG ILLIXR_VERSION="x"

FROM ${PARENT_IMAGE}:${ILLIXR_VERSION}

USER root
RUN apt-get update
RUN apt-get install -y portaudio19-dev libspatialaudio-dev

USER illixr
ENV USE_LGPL=1
