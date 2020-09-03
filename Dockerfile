FROM ubuntu:18.04

COPY . $HOME/ILLIXR/

ENV DEBIAN_FRONTEND noninteractive

WORKDIR ILLIXR

RUN apt update && apt install -y sudo apt-transport-https

RUN ./scripts/install_apt_deps.sh
RUN ./scripts/install_opencv.sh
RUN ./scripts/install_vulkan_headers.sh
RUN ./scripts/install_gtest.sh
RUN ./scripts/install_openxr.sh
RUN ./scripts/install_conda.sh && $HOME/miniconda3/bin/conda env create --force -f runner/environment.yml


ENTRYPOINT ./runner.sh configs/ci.yaml
