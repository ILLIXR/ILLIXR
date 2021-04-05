#!/bin/bash

#--- Script defining values to be used by accompanying install scripts ---#


### Setup ###

## Update the working directory
cd "$(dirname ${0})"

## Source the default values for imported variables
. scripts/default_values.sh

## Source the global helper functions
. scripts/system_utils.sh

## Source the configurations for our dependencies
. deps.sh

## Setup script
interp_cmd="python"
interp_args=""
script_path="runner/runner/main.py"
script_args="${@}"
venv_name="illixr-runner"


### Launch application

## If we find an installation log for conda, use the src_dir recorded
detect_dependency "${dep_name_conda}" "${deps_log_dir}" "yes"
if [ "$?" -eq 0 ]; then
    src_dir_conda="${src_dir}"
    dep_log_path_conda="${dep_log_path}"
else
    src_dir_conda="${parent_dir_conda}/${dep_name_conda}"
    dep_log_path_conda="${dep_log_path}"
    print_warning "Installation log for conda not found at '${dep_log_path_conda}'."
    echo "Checking in '${src_dir_conda}'."
fi

## Source the conda environment profile
. "${src_dir_conda}/etc/profile.d/conda.sh"

## Setup virtual environment
check_cmd_conda=$(conda --version 2>/dev/null)
if [ "$?" -eq 0 ]; then
    ## Conda found => Reset and activate the virtual environment
    conda deactivate
    conda activate "${venv_name}"
else
    print_warning "Conda not found. Attempting to run ILLIXR via '${interp_cmd}' anyway ..."
fi

## Start executing action from main
"${interp_cmd}" ${interp_args} "${script_path}" "${script_args}"
