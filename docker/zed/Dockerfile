ARG PARENT_IMAGE="x"
ARG ILLIXR_VERSION="x"

FROM ${PARENT_IMAGE}:${ILLIXR_VERSION}

ARG CUDA_MAJOR=12
ARG CUDA_MINOR=6
ARG CUDA_PATCH=3

ARG UBUNTU_RELEASE_YEAR=24

ARG ZED_SDK_MAJOR=4
ARG ZED_SDK_MINOR=2
ARG ZED_SDK_PATCH=3

USER root
RUN apt-get update
RUN apt-get install -y libzstd-dev
RUN apt-get install -y zstd libopenblas-dev libopenblas-pthread-dev

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
