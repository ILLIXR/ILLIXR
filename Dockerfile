ARG BASE_IMG=ubuntu:20.04

FROM ${BASE_IMG}

ARG BASE_IMG

ARG JOBS=1
ARG BUILD_TYPE=Release

ENV ENABLE_DOCKER=yes
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Europe/Moscow
ENV CC=clang-10
ENV CXX=clang++-10
ENV opt_dir=/opt/ILLIXR
ENV prefix_dir=/usr/local
ENV illixr_dir=${HOME}/ILLIXR
ENV illixr_nproc=${JOBS}
ENV build_type=${BUILD_TYPE}
ENV cache_path=.cache/paths

ENV use_realsense=yes

ENV src_dir_conda=${opt_dir}/miniconda3
ENV env_config_path=runner/environment.yml

ENV url_euroc='http://robotics.ethz.ch/~asl-datasets/ijrr_euroc_mav_dataset/vicon_room1/V1_02_medium/V1_02_medium.zip'
ENV data_dir_euroc=${illixr_dir}/${cache_path}/http%%c%%s%%srobotics.ethz.ch%%s~asl-datasets%%sijrr_euroc_mav_dataset%%svicon_room1%%sV1_02_medium%%sV1_02_medium.zip
ENV zip_path_euroc=${illixr_dir}/${cache_path}/http%c%s%srobotics.ethz.ch%s~asl-datasets%sijrr_euroc_mav_dataset%svicon_room1%sV1_02_medium%sV1_02_medium.zip
ENV sub_path_euroc=mav0

RUN mkdir -p ${opt_dir}

RUN ln -snf /usr/share/zoneinfo/${TZ} /etc/localtime && echo ${TZ} > /etc/timezone
RUN apt-get update && apt-get install -y sudo curl unzip doxygen

## Create illixr_dir, cache_path, data_dir_euroc, sub_path_euroc together
## Prevents Runner from fetching
RUN mkdir -p ${data_dir_euroc}/${sub_path_euroc}

## Download (and cache) the euroc dataset early in the build
RUN curl ${url_euroc} -o ${zip_path_euroc}
RUN unzip -d ${data_dir_euroc} ${zip_path_euroc} ${sub_path_euroc}/*

COPY ./deps.sh ${HOME}/deps.sh
COPY ./scripts/default_values.sh ${HOME}/scripts/default_values.sh
COPY ./scripts/system_utils.sh ${HOME}/scripts/system_utils.sh

COPY ./scripts/install_apt_deps.sh ${HOME}/scripts/install_apt_deps.sh
RUN env use_realsense=${use_realsense} ./scripts/install_apt_deps.sh
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

COPY ./scripts/install_vtk.sh ${HOME}/scripts/install_vtk.sh
RUN ./scripts/install_vtk.sh

COPY ./scripts/install_eigen.sh ${HOME}/scripts/install_eigen.sh
RUN ./scripts/install_eigen.sh

COPY ./scripts/install_opencv.sh ${HOME}/scripts/install_opencv.sh
RUN ./scripts/install_opencv.sh

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

COPY . ${illixr_dir}
WORKDIR ILLIXR

RUN ${src_dir_conda}/bin/conda env create --force -f ${env_config_path}
RUN python -m pip install mkdocs

RUN rm -rf ${opt_dir}/*/build # Save space by cleaning up

ENTRYPOINT                                                                 \
failed_ci=0;                                                               \
for action in ci clean ci-monado clean ci-monado-mainline clean docs; do   \
    echo "[${action}] Starting ...";                                       \
    echo "Disk usage:";                                                    \
    du -sh ${illixr_dir} ${opt_dir};                                       \
    env DISTRO_VER=${BASE_IMG#ubuntu:} ./runner.sh configs/${action}.yaml  \
    || failed_ci=1;                                                        \
done;                                                                      \
exit ${failed_ci}
