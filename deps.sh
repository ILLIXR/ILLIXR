#!/bin/bash

#--- Script defining values to be used by accompanying install scripts ---#


### Setup ###

## Source the default values for imported variables
if [ -z "${working_dir}" ]; then
    . scripts/default_values.sh
else
    ## 'working_dir' is not empty => Script was called from anonymous shell
    . "${working_dir}/scripts/default_values.sh"
fi

## Apt
export script_path_apt="scripts/install_apt_deps.sh"

## Clang
## Locally built clang not in use yet
#export dep_name_clang="clang"
#export script_path_clang="scripts/install_clang.sh"
#export parent_dir_clang="${opt_dir}"
#export dep_prompt_clang="Install Clang (and LLVM) from source"
#export dep_ver_clang="llvmorg-10.0.1"

## Boost
## Locally built boost not in use yet
#export dep_name_boost="boost"
#export script_path_boost="scripts/install_boost.sh"
#export parent_dir_boost="${opt_dir}"
#export dep_prompt_boost="Install Boost from source"
#export dep_ver_boost="boost-1.65.1"

## OpenCV
export dep_name_opencv="opencv"
export script_path_opencv="scripts/install_opencv.sh"
export parent_dir_opencv="${opt_dir}"
export dep_prompt_opencv="Install OpenCV from source"
export dep_ver_opencv="3.4.6-instrumented"

## Eigen
## Locally built eigen not in use yet
#export dep_name_eigen="eigen"
#export script_path_eigen="scripts/install_eigen.sh"
#export parent_dir_eigen="${opt_dir}"
#export dep_prompt_eigen="Install eigen (Eigen3) from source"
#export dep_ver_eigen="3.3.7"

## Vulkan (Headers)
export dep_name_vulkan="Vulkan-Headers"
export script_path_vulkan="scripts/install_vulkan_headers.sh"
export parent_dir_vulkan="${opt_dir}"
export dep_prompt_vulkan="Install Vulkan Headers from source"
export dep_ver_vulkan="v1.2.174"

## GoogleTest (gtest)
export dep_name_gtest="googletest"
export script_path_gtest="scripts/install_gtest.sh"
export parent_dir_gtest="${opt_dir}"
export dep_prompt_gtest="Install gtest"
export dep_ver_gtest="release-1.10.0"

## Qemu
export dep_name_qemu="qemu"
export script_path_qemu="scripts/install_qemu.sh"
export parent_dir_qemu="${opt_dir}"
export dep_prompt_qemu="Install qemu (not necessary for core ILLIXR; necessary for virtualization)"
export dep_ver_qemu="v5.1.0"

## OpenXR (SDK)
export dep_name_openxr="OpenXR-SDK"
export script_path_openxr="scripts/install_openxr.sh"
export parent_dir_openxr="${opt_dir}"
export dep_prompt_openxr="Install OpenXR SDK from source"
export dep_ver_openxr="release-1.0.14"

## GTSAM
export dep_name_gtsam="gtsam"
export script_path_gtsam="scripts/install_gtsam.sh"
export parent_dir_gtsam="${opt_dir}"
export dep_prompt_gtsam="Install gtsam from source"
export dep_ver_gtsam="kimera-gtsam"

## OpenGV
export dep_name_opengv="opengv"
export script_path_opengv="scripts/install_opengv.sh"
export parent_dir_opengv="${opt_dir}"
export dep_prompt_opengv="Install opengv from source"
export dep_ver_opengv="master"

## DBoW2
export dep_name_dbow2="DBoW2"
export script_path_dbow2="scripts/install_dbow2.sh"
export parent_dir_dbow2="${opt_dir}"
export dep_prompt_dbow2="Install DBoW2 from source"
export dep_ver_dbow2="v1.1-free"

## Kimera (RPGO)
export dep_name_kimera_rpgo="Kimera-RPGO"
export script_path_kimera_rpgo="scripts/install_kimera_rpgo.sh"
export parent_dir_kimera_rpgo="${opt_dir}"
export dep_prompt_kimera_rpgo="Install Kimera-RPGO from source"
export dep_ver_kimera_rpgo="dec-2020"

## Conda (miniconda)
export dep_name_conda="miniconda3"
export script_path_conda="scripts/install_conda.sh"
export parent_dir_conda="${opt_dir}"
export dep_prompt_conda="Install Conda"
export dep_ver_conda="py38_4.9.2"

## DepthAI
export dep_name_depthai="depthai"
export script_path_depthai="scripts/install_depthai.sh"
export parent_dir_depthai="${opt_dir}"
export dep_prompt_depthai="Install DepthAI from source"
export dep_ver_depthai="v2.5.0"


## OpenVINS
export dep_name_openvins="open_vins"
export script_path_openvins="scripts/install_openvins.sh"
export parent_dir_openvins="${opt_dir}"
export dep_prompt_openvins="Install OpenVINS from source"
export dep_ver_openvins="v2.6.3"
