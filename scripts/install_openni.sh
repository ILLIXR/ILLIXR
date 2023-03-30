#!/bin/bash

#--- Script for installing OpenNI ---#


### Setup ###

## Source the default values for imported variables
. scripts/default_values.sh

## Source the global helper functions
. scripts/system_utils.sh

## Source the configurations for our dependencies
. deps.sh


### Package metadata setup ###

if [ -z "${dep_name}" ]; then
    dep_name="${dep_name_openni}"
fi

if [ -z "${src_dir}" ]; then
    src_dir="${parent_dir_openni}/${dep_name_openni}"
fi

if [ -z "${dep_ver}" ]; then
    dep_ver="${dep_ver_openni}"
fi

repo_url="https://github.com/structureio/OpenNI2.git"

## Fetch
git clone --depth 1 --branch "${dep_ver}" "${repo_url}" "${src_dir}"

## Log
log_dependency "${dep_name}" "${deps_log_dir}" "${src_dir}" "${dep_ver}"
