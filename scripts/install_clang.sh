#!/bin/bash

#--- Script for installing Clang ---#

## Currently disabled: TBD
## Using system package 'clang-10' (see 'scripts/install_apt_deps.sh')


### Setup ###

## Source the default values for imported variables
. scripts/default_values.sh

## Source the global helper functions
. scripts/system_utils.sh

## Source the configurations for our dependencies
. deps.sh


### Package metadata setup ###

if [ -z "${dep_name}" ]; then
    dep_name="${dep_name_clang}"
fi

if [ -z "${src_dir}" ]; then
    src_dir="${parent_dir_clang}/${dep_name_clang}"
fi

if [ -z "${dep_ver}" ]; then
    dep_ver="${dep_ver_clang}"
fi

repo_url="https://github.com/llvm/llvm-project.git"
build_dir="${src_dir}/build"

cmd_clang_c="${prefix_dir}/bin/clang"
cmd_clang_cxx="${prefix_dir}/bin/clang++"

max_nproc_clang="4"
if [ "${illixr_nproc}" -gt "${max_nproc_clang}" ]; then
    ## Too many threads => run out of memory
    illixr_nproc="${max_nproc_clang}"
fi

llvm_projects=(
    libcxx
    libcxxabi
    lld
    clang
    clang-tools-extra
    compiler-rt
) # End list

llvm_projects_arg=$(join_strings "${llvm_projects[*]}")

## Speed up compilation by building only the native target backend
## To build more than one target, append the target to the list
## (with targets delimited by semicolons)
arch_name=$(uname --machine)
case "${arch_name}" in
    x86_64)
        llvm_targets_arg="X86"
        ;;
    aarch64)
        llvm_targets_arg="AAarch64"
        ;;
    *)
        print_warning "Unsupported target LLVM backend '${arch_name}'."
        exit 1
        ;;
esac

build_opts_llvm=(
    -D LLVM_ENABLE_PROJECTS="${llvm_projects_arg}"
    -D LLVM_TARGETS_TO_BUILD="${llvm_targets_arg}"
) # End list


### Checks ###

## Assert no system packages will be overwritten by this install
## If present, remove the conflicting packages before proceeding
pkg_list_clang="clang-10"
detect_packages "${pkg_list_clang}" "${PKG_MODE_FOUND_NONFATAL}"

### Fetch, build and install ###

## Fetch
git clone --depth 1 --branch "${dep_ver}" "${repo_url}" "${src_dir}"

## Build
cmake \
    -S "${src_dir}/llvm" \
    -B "${build_dir}" \
    -D CMAKE_C_COMPILER="gcc" \
    -D CMAKE_CXX_COMPILER="g++" \
    -D CMAKE_BUILD_TYPE="Release" \
    -D CMAKE_INSTALL_PREFIX="${prefix_dir}" \
    "${build_opts_llvm[@]}"
make -C "${build_dir}" -j "${illixr_nproc}"

## Install
sudo make -C "${build_dir}" -j "${illixr_nproc}" install

if [ ! -e "${cmd_clang_c}" ] && [ -e "${cmd_clang_c}-10" ]; then
    sudo ln -s "${cmd_clang_c}-10" "${cmd_clang_c}"
fi

if [ ! -e "${cmd_clang_cxx}" ] && [ -e "${cmd_clang_cxx}-10" ]; then
    sudo ln -s "${cmd_clang_cxx}-10" "${cmd_clang_cxx}"
fi

## Log
log_dependency "${dep_name}" "${deps_log_dir}" "${src_dir}" "${dep_ver}"
