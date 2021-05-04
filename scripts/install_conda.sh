#!/bin/bash

#--- Script for installing python (miniconda) ---#


### Setup ###

## Source the default values for imported variables
. scripts/default_values.sh

## Source the global helper functions
. scripts/system_utils.sh

## Source the configurations for our dependencies
. deps.sh


### Package metadata setup ###

if [ -z "${dep_name}" ]; then
    dep_name="${dep_name_conda}"
fi

if [ -z "${src_dir}" ]; then
    src_dir="${parent_dir_conda}/${dep_name_conda}"
fi

if [ -z "${dep_ver}" ]; then
    dep_ver="${dep_ver_conda}"
fi

arch_name=$(uname --machine)
script_url="https://repo.anaconda.com/miniconda/Miniconda3-${dep_ver}-Linux-${arch_name}.sh"
script_path="${src_dir}/miniconda.sh"


### Checks ###

case "${arch_name}" in
    x86_64)
        check_cmd_conda=$(conda --version 2>/dev/null)
        if [ "$?" -eq 0 ]; then
            ## Conda found
            echo "Found conda installation. Exiting installation script."
            exit 0
        fi
        ;;
    *)
        ## No support for other architectures (e.g. ppc64le, aarch64)
        exit 0
        ;;
esac


### Fetch, build and install ###

## Fetch
mkdir -p "${src_dir}"
wget "${script_url}" -O "${script_path}"
chmod ug+x "${script_path}"

## Install
"${script_path}" -b -f -p "${src_dir}"

## Log
log_dependency "${dep_name}" "${deps_log_dir}" "${src_dir}" "${dep_ver}"
