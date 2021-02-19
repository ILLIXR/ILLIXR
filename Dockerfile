FROM ubuntu:18.04

ARG JOBS=1

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Europe/Moscow
ENV CC=clang-10
ENV CXX=clang++-10
ENV temp_dir /tmp/ILLIXR
ENV opt_dir /opt/ILLIXR
ENV illixr_nproc ${JOBS}

RUN mkdir -p ${temp_dir}
RUN mkdir -p ${opt_dir}

RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone
RUN apt-get update && apt-get install -y sudo

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

COPY ./scripts/install_gtsam.sh $HOME/scripts/install_gtsam.sh
RUN ./scripts/install_gtsam.sh

COPY ./scripts/install_opengv.sh $HOME/scripts/install_opengv.sh
RUN ./scripts/install_opengv.sh

COPY ./scripts/install_dbow2.sh $HOME/scripts/install_dbow2.sh
RUN ./scripts/install_dbow2.sh

COPY ./scripts/install_kimera_rpgo.sh $HOME/scripts/install_kimera_rpgo.sh
RUN ./scripts/install_kimera_rpgo.sh

COPY ./scripts/install_conda.sh $HOME/scripts/install_conda.sh
RUN ./scripts/install_conda.sh

RUN ldconfig

COPY . $HOME/ILLIXR/
WORKDIR ILLIXR
RUN $HOME/miniconda3/bin/conda env create --force -f runner/environment.yml

ENTRYPOINT ./runner.sh configs/ci.yaml
