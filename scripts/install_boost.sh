#!/bin/bash

#--- Script for installing Boost ---#

## Currently disabled: GTSAM fails to find library 'program_options'
## Using system package 'libboost-all-dev' (see 'scripts/install_apt_deps.sh')


### Setup ###

## Source the default values for imported variables
. scripts/default_values.sh

## Source the global helper functions
. scripts/system_utils.sh

## Source the configurations for our dependencies
. deps.sh


### Package metadata setup ###

if [ -z "${dep_name}" ]; then
    dep_name="${dep_name_boost}"
fi

if [ -z "${src_dir}" ]; then
    src_dir="${parent_dir_boost}/${dep_name_boost}"
fi

if [ -z "${dep_ver}" ]; then
    dep_ver="${dep_ver_boost}"
fi

repo_url="https://github.com/boostorg/boost.git"
build_dir="${src_dir}/build"

## Function to export Boost variable given its source directory
#> Should be moved to 'deps.sh' if compiling other dependencies
#> with Boost
function source_env_boost()
{
    local src_dir="${1}"

    export BOOST_ROOT="${src_dir}/boost_root"
    export BOOST_INCLUDEDIR="${BOOST_ROOT}/include"
    export BOOST_LIBRARYDIR="${BOOST_ROOT}/lib"

    export build_opts_boost=(
        -D BOOST_ROOT="${BOOST_ROOT}"
        -D BOOST_INCLUDEDIR="${BOOST_INCLUDEDIR}"
        -D BOOST_LIBRARYDIR="${BOOST_LIBRARYDIR}"
        -D Boost_NO_SYSTEM_PATHS=ON
        -D Boost_NO_BOOST_CMAKE=ON
    ) # End list
}

source_env_boost "${src_dir}"
prefix_dir="${BOOST_ROOT}"

case "${build_type}" in
    Release)        build_variant="release"
                    ;;
    RelWithDebInfo) build_variant="release" # This is a large build
                    ;;
    Debug)          build_variant="debug"
                    ;;
    *)              print_warning "Unexpected build_type '${build_type}'."
                    exit 1
                    ;;
esac

build_opts_boost_b2=(
    toolset="clang"
    variant="${build_variant}"
    --layout="versioned"
    --build-type="complete"
    --build-dir="${build_dir}"
    -j "${illixr_nproc}"
) # End list

libs_boost=(
    all
    #serialization
    #system
    #filesystem
    #thread
    #program_options
    #date_time
    #timer
    #chrono
    #regex
) # End list

build_opts_boost_libs=$(join_strings "${libs_boost[*]}" ",")


### Checks ###

## Assert no system packages will be overwritten by this install
## If present, remove the conflicting packages before proceeding
pkg_list_boost="libboost-all-dev"
detect_packages "${pkg_list_boost}" "${PKG_MODE_FOUND_NONFATAL}"


### Fetch, build and install ###

## Fetch
git clone --depth 1 --branch "${dep_ver}" "${repo_url}" "${src_dir}"
cd "${src_dir}"
git submodule update --init

## Build
mkdir -p "${build_dir}"

"${src_dir}/bootstrap.sh" \
    --prefix="${prefix_dir}" \
    --with-libraries="${build_opts_boost_libs}" \
    --with-"${build_opts_boost_b2[0]}" # --with-toolset=clang

log_error_boost="${src_dir}/bootstrap.log"
if [ -f "${log_error_boost}" ]; then
    cat "${log_error_boost}"
fi

"${src_dir}/b2" "${build_opts_boost_b2[@]}"

## Install
sudo "${src_dir}/b2" "${build_opts_boost_b2[@]}" install
cd -

## Log
log_dependency "${dep_name}" "${deps_log_dir}" "${src_dir}" "${dep_ver}"
