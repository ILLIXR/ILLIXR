#!/bin/bash

#--- Script defining values to be used by accompanying install scripts ---#


### Default imported variables setup ###

export CC="${CC:=clang-10}"
export CXX="${CXX:=clang++-10}"

export opt_dir="${opt_dir:=/opt/ILLIXR}"
export prefix_dir="${prefix_dir:=/usr/local}"
export illixr_nproc="${illixr_nproc:=1}"
export build_type="${build_type:=RelWithDebInfo}"
export deps_log_dir="${deps_log_dir:=.cache/deps}"
export env_config_path="${env_config_path:=runner/environment.yml}"


### For use with 'scripts/install_apt_deps.sh' ###

export use_realsense="${use_realsense:=no}"
export use_docker="${use_docker:=no}"
export use_cuda="${use_cuda:=no}"
