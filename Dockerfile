FROM ubuntu:20.04

ARG JOBS=1
ARG ACTION=ci

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Europe/Moscow
ENV CC=clang-10
ENV CXX=clang++-10
ENV opt_dir=/opt/ILLIXR
ENV prefix_dir=/usr/local
ENV illixr_nproc=${JOBS}
ENV build_type=Release

ENV src_dir_conda=${opt_dir}/miniconda3
ENV env_config_path=runner/environment.yml
ENV runner_action=configs/${ACTION}.yaml

RUN mkdir -p ${opt_dir}

RUN ln -snf /usr/share/zoneinfo/${TZ} /etc/localtime && echo ${TZ} > /etc/timezone
RUN apt-get update && apt-get install -y sudo

COPY ./deps.sh ${HOME}/deps.sh
COPY ./scripts/default_values.sh ${HOME}/scripts/default_values.sh
COPY ./scripts/system_utils.sh ${HOME}/scripts/system_utils.sh

COPY ./scripts/install_apt_deps.sh ${HOME}/scripts/install_apt_deps.sh
RUN ./scripts/install_apt_deps.sh
RUN apt-get autoremove -y # Save space by cleaning up

## Locally built clang not in use yet
#COPY ./scripts/install_clang.sh ${HOME}/scripts/install_clang.sh
#RUN ./scripts/install_clang.sh

## Make clang symlinks in the prefix dir
RUN ln -s $(which ${CC}) ${prefix_dir}/bin/clang
RUN ln -s $(which ${CXX}) ${prefix_dir}/bin/clang++

## Locally built boost not in use yet
#COPY ./scripts/install_boost.sh ${HOME}/scripts/install_boost.sh
#RUN ./scripts/install_boost.sh

COPY ./scripts/install_opencv.sh ${HOME}/scripts/install_opencv.sh
RUN ./scripts/install_opencv.sh

## Locally built eigen not in use yet
#COPY ./scripts/install_eigen.sh ${HOME}/scripts/install_eigen.sh
#RUN ./scripts/install_eigen.sh

COPY ./scripts/install_vulkan_headers.sh ${HOME}/scripts/install_vulkan_headers.sh
RUN ./scripts/install_vulkan_headers.sh

COPY ./scripts/install_gtest.sh ${HOME}/scripts/install_gtest.sh
RUN ./scripts/install_gtest.sh

COPY ./scripts/install_openxr.sh ${HOME}/scripts/install_openxr.sh
RUN ./scripts/install_openxr.sh

COPY ./scripts/install_gtsam.sh ${HOME}/scripts/install_gtsam.sh
RUN ./scripts/install_gtsam.sh

COPY ./scripts/install_opengv.sh ${HOME}/scripts/install_opengv.sh
RUN ./scripts/install_opengv.sh

COPY ./scripts/install_dbow2.sh ${HOME}/scripts/install_dbow2.sh
RUN ./scripts/install_dbow2.sh

COPY ./scripts/install_kimera_rpgo.sh ${HOME}/scripts/install_kimera_rpgo.sh
RUN ./scripts/install_kimera_rpgo.sh

COPY ./scripts/install_conda.sh ${HOME}/scripts/install_conda.sh
RUN ./scripts/install_conda.sh

RUN ldconfig

COPY . ${HOME}/ILLIXR/
WORKDIR ILLIXR
RUN ${src_dir_conda}/bin/conda env create --force -f ${env_config_path}

ENTRYPOINT ./runner.sh ${runner_action}
