FROM ubuntu:18.04

ENV DEBIAN_FRONTEND noninteractive

RUN apt update && apt install -y sudo apt-transport-https

COPY ./scripts/install_apt_deps.sh $HOME/scripts/install_apt_deps.sh
RUN ./scripts/install_apt_deps.sh

COPY ./scripts/install_opencv.sh $HOME/scripts/install_opencv.sh
RUN ./scripts/install_opencv.sh

COPY ./scripts/install_vulkan_headers.sh $HOME/scripts/install_vulkan_headers.sh
RUN ./scripts/install_vulkan_headers.sh

COPY ./scripts/install_gtest.sh $HOME/scripts/install_gtest.sh
RUN ./scripts/install_gtest.sh

COPY ./scripts/install_openxr.sh $HOME/scripts/install_openxr.sh
RUN ./scripts/install_openxr.sh

COPY ./scripts/install_conda.sh $HOME/scripts/install_conda.sh
RUN ./scripts/install_conda.sh

COPY . $HOME/ILLIXR/
WORKDIR ILLIXR
RUN $HOME/miniconda3/bin/conda env create --force -f runner/environment.yml

ENTRYPOINT ./runner.sh configs/ci.yaml
